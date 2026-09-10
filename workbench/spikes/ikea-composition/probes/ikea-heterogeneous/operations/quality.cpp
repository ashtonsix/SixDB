#include "../analyse.h"
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace fs = std::filesystem;
using namespace ikea::heterogeneous;
using Window = std::array<std::uint8_t, analyse_plain_bytes>;

std::string quoted(std::string_view value) {
    std::string result = "\"";
    for(char c : value) { result += c; if(c == '"') result += c; }
    return result + '"';
}

// The adapter's sidecar is a flat object. Only its three unsigned scalar fields
// are consumed here; full prepared-input identity verification belongs to Run.
std::uint64_t index_number(const std::string& line, std::string_view name) {
    const std::string key = '"' + std::string(name) + '"';
    auto start = line.find(key);
    if(start == std::string::npos) throw std::runtime_error("Missing sidecar field " + key);
    start += key.size();
    while(start < line.size() && line[start] == ' ') ++start;
    if(start == line.size() || line[start++] != ':') throw std::runtime_error("Invalid sidecar field " + key);
    while(start < line.size() && line[start] == ' ') ++start;
    std::uint64_t number = 0;
    const auto parsed = std::from_chars(line.data() + start, line.data() + line.size(), number);
    if(parsed.ec != std::errc{} || parsed.ptr == line.data() + line.size() ||
       (*parsed.ptr != ',' && *parsed.ptr != '}' && *parsed.ptr != ' '))
        throw std::runtime_error("Invalid sidecar integer " + key);
    return number;
}

unsigned population(const Window& input) {
    unsigned sum = 0;
    for(auto byte : input) sum += std::popcount(byte);
    return sum;
}

void emit(const Window& input, std::string_view corpus, std::string_view archive,
          std::string_view family, std::string_view partition, std::uint64_t ordinal,
          unsigned pop) {
    std::array<std::uint8_t, analyse_encode_writable_bytes> body;
    const unsigned actual = encode_body_bytes(input.data(), body.data());
    if(actual > analyse_max_body_bytes) throw std::runtime_error("Native body exceeds format maximum");
    for(auto model : {AnalyseModel::cheap, AnalyseModel::quadrants}) {
        for(auto scan : {AnalyseScan::full, AnalyseScan::sample32}) {
            const unsigned predicted = predict_body_bytes(input.data(), model, scan);
            std::cout << quoted(corpus) << ',' << quoted(archive) << ',' << quoted(family) << ','
                      << partition << ',' << ordinal << ',' << pop << ','
                      << (model == AnalyseModel::cheap ? "cheap" : "quadrants") << ','
                      << analyse_scan_name(scan) << ',' << actual << ',' << predicted << '\n';
        }
    }
}

std::string roaring_family(const std::string& name) {
    if(name.starts_with("dimension_")) return "dimension";
    if(name.ends_with("_srt")) return name.substr(0, name.size() - 4);
    return name;
}

std::string partition_for(std::string_view family) {
    if(family == "census-income") return "test_comparison";
    if(family == "census1881") return "validation_comparison";
    if(family == "dimension" || family == "uscensus2000" || family == "weather_sept_85" ||
       family == "wikileaks-noquotes" || family == "msmarco") return "train_family_comparison";
    throw std::runtime_error("Unknown source family: " + std::string(family));
}

std::uint64_t corpus(const fs::path& directory, bool marco) {
    std::vector<fs::path> files;
    for(const auto& entry : fs::directory_iterator(directory))
        if(entry.is_regular_file() && entry.path().extension() == ".kw16") files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    if(files.size() != (marco ? 16u : 12u))
        throw std::runtime_error("Expected all " + std::to_string(marco ? 16 : 12) +
                                 " prepared archives in " + directory.string());
    std::uint64_t total = 0;
    for(const auto& path : files) {
        const auto name = path.stem().string();
        const auto family = marco ? "msmarco" : roaring_family(name);
        const auto partition = partition_for(family);
        std::ifstream data(path, std::ios::binary);
        std::ifstream index(path.parent_path() / (name + ".windows.jsonl"));
        if(!data || !index) throw std::runtime_error("Cannot open payload/sidecar for " + name);
        std::uint64_t ordinal = 0, offset = 0;
        std::array<std::uint8_t, 2 * 65536> positions;
        Window input;
        while(true) {
            std::array<unsigned char, 4> header;
            data.read(reinterpret_cast<char*>(header.data()), header.size());
            if(data.gcount() == 0 && data.eof()) break;
            if(data.gcount() != 4) throw std::runtime_error("Truncated cardinality: " + name);
            const unsigned card = unsigned(header[0]) | (unsigned(header[1]) << 8) |
                                  (unsigned(header[2]) << 16) | (unsigned(header[3]) << 24);
            if(card == 0 || card > 65536) throw std::runtime_error("Invalid prepared cardinality: " + name);
            std::string line;
            if(!std::getline(index, line) || index_number(line, "ordinal") != ordinal ||
               index_number(line, "offset") != offset || index_number(line, "cardinality") != card)
                throw std::runtime_error("Payload/sidecar mismatch: " + name + ":" + std::to_string(ordinal));
            data.read(reinterpret_cast<char*>(positions.data()), card * 2);
            if(data.gcount() != std::streamsize(card * 2)) throw std::runtime_error("Truncated positions: " + name);
            input.fill(0);
            int previous = -1;
            for(unsigned i = 0; i != card; ++i) {
                const unsigned p = unsigned(positions[2 * i]) | (unsigned(positions[2 * i + 1]) << 8);
                if(int(p) <= previous) throw std::runtime_error("Positions are not strictly increasing: " + name);
                previous = int(p);
                input[p / 8] |= std::uint8_t(1u << (p % 8));
            }
            emit(input, marco ? "msmarco-keyset" : "real-roaring", name, family, partition, ordinal, card);
            ++ordinal;
            offset += 4 + 2 * card;
        }
        std::string extra;
        if(data.bad() || index.bad() || std::getline(index, extra))
            throw std::runtime_error("Read error or extra sidecar row: " + name);
        if(ordinal == 0) throw std::runtime_error("Empty prepared archive: " + name);
        std::cerr << "archive=" << name << " windows=" << ordinal << " input_bytes=" << offset << '\n';
        total += ordinal;
    }
    return total;
}

std::uint64_t random64(std::uint64_t& state) {
    auto x = (state += 0x9e3779b97f4a7c15ULL);
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

unsigned structural() {
    Window random;
    std::uint64_t state = 0x6b77696e646f7731ULL;
    for(auto& byte : random) byte = std::uint8_t(random64(state));
    unsigned windows = 0;
    auto output = [&](const Window& input, const char* archive, const char* family, unsigned ordinal) {
        emit(input, "structural", archive, family, "structural_comparison", ordinal, population(input));
        ++windows;
    };
    // Sweep every dense-tile count without choosing a range after observing a
    // crossover. The prefix/permutation pair holds the tile multiset constant.
    for(unsigned dense = 0; dense <= 256; ++dense) {
        Window prefix{}, permuted{}, repeated{};
        for(unsigned tile = 0; tile != dense; ++tile) {
            std::copy_n(random.data() + tile * 32, 32, prefix.data() + tile * 32);
            std::copy_n(random.data() + tile * 32, 32, permuted.data() + ((73 * tile + 19) & 255) * 32);
            std::fill_n(repeated.data() + tile * 32, 32, std::uint8_t(0x55));
        }
        output(prefix, "random-dense-prefix", "dense-mixture", dense);
        output(permuted, "random-dense-permuted", "dense-mixture", dense);
        output(repeated, "repeated-byte-prefix", "byte-structure", dense);
    }
    // A phase-zero mask selects exactly the analyser's sample indices. Other
    // phases shift the selected tile inside each stratum. The inverse reverses
    // which side of the sample is dense; neither estimate is a bound.
    for(unsigned phase = 0; phase != 8; ++phase) {
        Window selected{}, inverse;
        inverse.fill(0x55);
        for(unsigned s = 0; s != 32; ++s) {
            const unsigned tile = 8 * s + ((analyse_sample_tile(s) + phase) & 7);
            std::fill_n(selected.data() + tile * 32, 32, std::uint8_t(0x55));
            std::fill_n(inverse.data() + tile * 32, 32, std::uint8_t(0));
        }
        output(selected, "sample-phase-dense", "sample-alias", phase);
        output(inverse, "sample-phase-zero", "sample-alias", phase);
    }
    // Repeated bytes and quarter clustering deliberately expose structure that
    // a population/enumerative-cost-only estimate cannot distinguish fully.
    for(unsigned value = 0; value != 256; ++value) {
        Window repeated;
        repeated.fill(std::uint8_t(value));
        output(repeated, "repeated-byte-homogeneous", "byte-structure", value);
    }
    for(unsigned rotation = 0; rotation != 32; ++rotation) {
        Window clustered{};
        for(unsigned tile = 0; tile != 256; ++tile)
            for(unsigned byte = 0; byte != 16; ++byte)
                clustered[tile * 32 + ((byte + rotation) & 31)] = 0xff;
        output(clustered, "half-window-per-tile-rotation", "byte-structure", rotation);
    }
    return windows;
}
} // namespace

int main(int argc, char** argv) {
    try {
        fs::path roaring, marco;
        for(int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if(arg == "--help") {
                std::cout << "quality --roaring PREPARED_DIR --msmarco PREPARED_DIR\n"
                             "Streams all prepared windows, then deterministic structural comparisons.\n";
                return 0;
            }
            if(i + 1 == argc || (arg != "--roaring" && arg != "--msmarco"))
                throw std::runtime_error("Usage: quality --roaring DIR --msmarco DIR");
            auto& destination = arg == "--roaring" ? roaring : marco;
            if(!destination.empty()) throw std::runtime_error("Duplicate argument");
            destination = argv[++i];
        }
        if(roaring.empty() || marco.empty()) throw std::runtime_error("Both prepared corpora are required");
        std::ios::sync_with_stdio(false);
        std::cout << "corpus,archive,family,partition,ordinal,population,model,scan,actual_body_bytes,predicted_body_bytes\n";
        std::cerr << "cheap_model=" << analyse_model_id(AnalyseModel::cheap) << '\n'
                  << "quadrants_model=" << analyse_model_id(AnalyseModel::quadrants) << '\n'
                  << "structure_seed=0x6b77696e646f7731; no model or margin fitting\n";
        const auto natural = corpus(roaring, false) + corpus(marco, true);
        const auto stress = structural();
        std::cout.flush();
        if(!std::cout) throw std::runtime_error("CSV output failed");
        std::cerr << "natural_windows=" << natural << " structural_windows=" << stress
                  << " rows=" << 4 * (natural + stress) << '\n';
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "quality: " << error.what() << '\n';
        return 1;
    }
}

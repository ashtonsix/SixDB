// Actual pinned upstream primitives; no substitute codec or upstream edits.
#pragma STDC FENV_ACCESS ON
#include <alp.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace {
constexpr auto N = alp::config::VECTOR_SIZE;
void need(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "audit fixture: %s\n", why); std::exit(2); }
}
template<class T> using U = typename alp::inner_t<T>::ut;
template<class T> using I = typename alp::inner_t<T>::st;
template<class T> const char* type_name() { return sizeof(T) == 4 ? "float" : "double"; }
template<class T> U<T> bits(const T& value) { return std::bit_cast<U<T>>(value); }
template<class T> void put(T& value, U<T> representation) { std::memcpy(&value, &representation, sizeof(T)); }
std::uint64_t mix(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}
struct environment {
    std::fenv_t old;
    std::uint64_t control = 0;
    environment() {
        need(std::fegetenv(&old) == 0, "fegetenv");
#if defined(__aarch64__)
        asm volatile("mrs %0, fpcr" : "=r"(control));
#endif
    }
    ~environment() {
        std::fesetenv(&old);
#if defined(__aarch64__)
        asm volatile("msr fpcr, %0\nisb" :: "r"(control) : "memory");
#endif
    }
    static void rounding(unsigned mode) {
        constexpr int modes[] = {FE_TONEAREST, FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO};
        need(mode < 4 && std::fesetround(modes[mode]) == 0, "fesetround");
    }
    static void flush(bool on) {
#if defined(__aarch64__)
        std::uint64_t v;
        asm volatile("mrs %0, fpcr" : "=r"(v));
        v = on ? v | (1ull << 24) : v & ~(1ull << 24);
        asm volatile("msr fpcr, %0\nisb" :: "r"(v) : "memory");
#else
        need(!on, "FZ probe needs AArch64");
#endif
    }
};

template<class T> void special_values(std::span<T> data) {
    constexpr std::uint64_t f64[] = {
        0, 0x8000000000000000ull, 0x7ff0000000000000ull, 0xfff0000000000000ull,
        0x7ff8000000000001ull, 0xfff8123456789abcull, 0x7ff0000000000001ull,
        1, 0x8000000000000001ull, 0xfffffffffffffull, 0x10000000000000ull,
        0x7fefffffffffffffull, 0xffefffffffffffffull, 0x3ff0000000000001ull};
    constexpr std::uint32_t f32[] = {
        0, 0x80000000u, 0x7f800000u, 0xff800000u, 0x7fc00001u, 0xffc12345u,
        0x7f800001u, 1, 0x80000001u, 0x7fffffu, 0x800000u, 0x7f7fffffu,
        0xff7fffffu, 0x3f800001u};
    for (std::size_t i = 0; i < data.size(); ++i) {
        if constexpr (sizeof(T) == 4) put(data[i], f32[i % std::size(f32)]);
        else put(data[i], f64[i % std::size(f64)]);
    }
}
// Stable out-of-line call keeps the actual upstream decode loop inspectable.
template<class T> [[gnu::noinline]] void rd_decode(T* out, U<T>* right,
        std::uint16_t* left, std::uint16_t* exceptions, std::uint16_t* positions,
        std::uint16_t* count, alp::state<T>& state) {
    alp::rd_encoder<T>::decode(out, right, left, exceptions, positions, count, state);
}
struct counts {
    std::size_t vectors = 0, mismatches = 0, exceptions = 0;
    std::size_t raw_outside_allocation = 0, decoded_inactive_slots = 0;
    unsigned max_raw = 0, max_decoded = 0;
};
template<class T> void rd_vector(const T* input, alp::state<T>& state, counts& totals, bool skip_packing = false) {
    std::array<std::uint16_t, N> positions{}, exceptions{}, left{}, packed_left{}, unpacked_left{};
    std::array<U<T>, N> right{}, packed_right{}, unpacked_right{};
    std::array<T, N> output{};
    std::uint16_t count = 0;
    alp::rd_encoder<T>::encode(input, exceptions.data(), positions.data(), &count, right.data(), left.data(), state);
    need(count <= N && state.actual_dictionary_size > 0 && state.actual_dictionary_size <= 8 &&
        state.left_bit_width >= 1 && state.left_bit_width <= 3, "RD fixture dimensions");
    std::array<bool, N> marked{};
    for (unsigned i = 0; i < count; ++i) {
        need(positions[i] < N && !marked[positions[i]], "independent exception position bounds/uniqueness");
        marked[positions[i]] = true;
        need(exceptions[i] == (bits(input[positions[i]]) >> state.right_bit_width), "independent exception high bits");
    }
    for (unsigned i = 0; i < N; ++i) {
        totals.max_raw = std::max(totals.max_raw, unsigned(left[i]));
        totals.raw_outside_allocation += left[i] >= 8;
        need(marked[i] == (left[i] >= state.actual_dictionary_size), "exception membership");
    }
    if (skip_packing) {
        std::printf("{\"control\":\"omitted_FFOR\",\"dict\":%u,\"exceptions\":%u,\"max_raw\":%u}\n",
                    state.actual_dictionary_size, count, totals.max_raw);
        std::fflush(stdout);
        need(totals.max_raw >= 8, "negative control must exceed allocated dictionary");
        rd_decode(output.data(), right.data(), left.data(), exceptions.data(), positions.data(), &count, state);
    } else {
        ffor::ffor(right.data(), packed_right.data(), state.right_bit_width, &state.right_for_base);
        ffor::ffor(left.data(), packed_left.data(), state.left_bit_width, &state.left_for_base);
        unffor::unffor(packed_right.data(), unpacked_right.data(), state.right_bit_width, &state.right_for_base);
        unffor::unffor(packed_left.data(), unpacked_left.data(), state.left_bit_width, &state.left_for_base);
        const unsigned mask = (1u << state.left_bit_width) - 1;
        for (unsigned i = 0; i < N; ++i) {
            need(unpacked_right[i] == right[i], "right-part pack/unpack");
            need(unpacked_left[i] == (left[i] & mask), "independent left-code truncation law");
            need(unpacked_left[i] < 8, "allocated dictionary bound after FFOR");
            totals.max_decoded = std::max(totals.max_decoded, unsigned(unpacked_left[i]));
            totals.decoded_inactive_slots += unpacked_left[i] >= state.actual_dictionary_size;
        }
        rd_decode(output.data(), unpacked_right.data(), unpacked_left.data(), exceptions.data(), positions.data(), &count, state);
    }
    for (unsigned i = 0; i < N; ++i) totals.mismatches += bits(input[i]) != bits(output[i]);
    totals.exceptions += count; ++totals.vectors;
}
template<class T> void report_rd(const char* label, const alp::state<T>& state, const counts& c) {
    std::printf("{\"path\":\"RD\",\"type\":\"%s\",\"case\":\"%s\",\"vectors\":%zu,\"dict\":%u,\"left_width\":%u,"
        "\"right_width\":%u,\"exceptions\":%zu,\"raw_outside_allocation\":%zu,\"decoded_inactive_slots\":%zu,"
        "\"max_raw\":%u,\"max_decoded\":%u,\"mismatches\":%zu}\n", type_name<T>(), label, c.vectors,
        state.actual_dictionary_size, state.left_bit_width, state.right_bit_width, c.exceptions,
        c.raw_outside_allocation, c.decoded_inactive_slots, c.max_raw, c.max_decoded, c.mismatches);
}
template<class T> T raw_value(unsigned prefix, std::uint64_t row) {
    constexpr auto low = sizeof(T) * 8 - 16;
    T out; put(out, (U<T>(prefix) << low) | (U<T>(mix(row)) & ((U<T>{1} << low) - 1))); return out;
}
template<class T> int rd_case(unsigned cardinality, unsigned population, bool raw) {
    need(cardinality >= 1 && cardinality <= 8 && population <= 4, "RD case arguments");
    std::vector<T> training(N * 25); // Three sampled vectors: 96 scalar samples.
    for (std::size_t i = 0; i < training.size(); ++i) training[i] = raw_value<T>(0x2000 + 2 * ((i / 32) % cardinality), i);
    if (population == 4) { // Ten sampled keys: retain eight plus exception-map entries.
        need(cardinality == 8, "mapped exceptions use full dictionary");
        training[0] = raw_value<T>(0x2001, 0); training[32] = raw_value<T>(0x2003, 32);
    }
    std::array<T, N> sample{}, input{};
    alp::state<T> state;
    alp::rd_encoder<T>::init(training.data(), 0, training.size(), sample.data(), state);
    need(state.actual_dictionary_size == cardinality, "constructed active dictionary cardinality");
    for (unsigned i = 0; i < N; ++i) input[i] = raw_value<T>(0x2000 + 2 * (i % cardinality), i + 1);
    for (unsigned i = 0; i < N; ++i) {
        const bool excluded = population == 1 ? i == 17 : population == 2 ? i != 17 : population == 3;
        if (excluded) input[i] = raw_value<T>(0x6000 + i % 32, i);
    }
    if (population == 4) {
        unsigned code = 0;
        for (auto [key, idx] : state.left_parts_dict_map) if (idx >= 8) { code = key; break; }
        need(code != 0 && state.right_bit_width == sizeof(T) * 8 - 16, "mapped out-of-allocation code fixture");
        for (unsigned i = 0; i < N; ++i) input[i] = raw_value<T>(code, i);
    }
    counts c; rd_vector(input.data(), state, c, raw);
    const std::string label = "dictionary" + std::to_string(cardinality) + "_population" + std::to_string(population);
    report_rd<T>(label.c_str(), state, c);
    return c.mismatches != 0;
}
template<class T> int rd_special(unsigned rounding, bool flush) {
    environment saved; environment::rounding(rounding); environment::flush(flush);
    std::array<T, N> input{}, sample{}; special_values<T>(input);
    alp::state<T> state; alp::rd_encoder<T>::init(input.data(), 0, N, sample.data(), state);
    counts c; rd_vector(input.data(), state, c);
    report_rd<T>(flush ? "special_IEEE_FZ" : "special_IEEE", state, c);
    return c.mismatches != 0;
}
int rd_corpus(const char* path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    need(bool(stream), "open corpus"); const auto size = stream.tellg();
    need(size > 0 && std::size_t(size) % (N * 8) == 0 && std::size_t(size) <= 8 * 1024 * 1024, "bounded complete corpus vectors");
    std::vector<double> values(std::size_t(size) / 8); stream.seekg(0); stream.read(reinterpret_cast<char*>(values.data()), size);
    need(bool(stream) && std::endian::native == std::endian::little, "original little-endian IEEE corpus bytes");
    std::array<double, N> sample{}; alp::state<double> state;
    alp::rd_encoder<double>::init(values.data(), 0, values.size(), sample.data(), state);
    counts c;
    for (std::size_t i = 0; i < values.size(); i += N) rd_vector(values.data() + i, state, c);
    report_rd<double>(path, state, c); return c.mismatches != 0;
}
template<class T> [[gnu::noinline]] void decimal_encode(const T* input, T* exceptions,
        std::uint16_t* positions, std::uint16_t* count, I<T>* integers, alp::state<T>& state) {
    alp::encoder<T>::encode(input, exceptions, positions, count, integers, state);
}
template<class T> int decimal(const std::string& shape, unsigned encoding_mode, unsigned decoding_mode, bool flush,
                             const T* original = nullptr) {
    environment saved; environment::rounding(0); environment::flush(false);
    std::array<T, N> training{}, input{}, sample{}, output{}, exceptions{};
    for (unsigned i = 0; i < N; ++i) training[i] = T(int(i % 100) - 50) / T{100};
    if (original) std::copy_n(original, N, training.data());
    input = training;
    const bool train_on_special = shape.starts_with("train-");
    const std::string values = train_on_special ? shape.substr(6) : shape;
    const unsigned position = train_on_special ? 0 : 1; // Row zero is sampled.
    if (values == "zero") input[position] = -T{0};
    else if (values == "infinity") input[position] = std::numeric_limits<T>::infinity();
    else if (values == "nan") put(input[position], sizeof(T) == 4 ? U<T>(0x7fc12345u) : U<T>(0x7ff8123456789abcull));
    else if (values == "denorm") put(input[position], U<T>{1});
    else if (values == "large") input[position] = std::numeric_limits<T>::max();
    else if (values == "mostly_special") { special_values<T>(input); input[17] = T{1}; }
    else need(values == "plain", "decimal shape");
    if (train_on_special) training = input;
    environment::rounding(encoding_mode); environment::flush(flush); std::feclearexcept(FE_ALL_EXCEPT);
    alp::state<T> state; alp::encoder<T>::init(training.data(), 0, N, sample.data(), state);
    std::printf("{\"stage\":\"init\",\"type\":\"%s\",\"scheme\":%u,\"shape\":\"%s\",\"enc_round\":%u,\"dec_round\":%u,\"flush\":%s}\n",
        type_name<T>(), unsigned(state.scheme), shape.c_str(), encoding_mode, decoding_mode, flush ? "true" : "false");
    std::fflush(stdout);
    if (state.scheme != alp::Scheme::ALP) {
        need(state.scheme == alp::Scheme::ALP_RD, "valid automatic route");
        // Sampling's route is an observation, not an ALP applicability promise.
        alp::rd_encoder<T>::init(training.data(), 0, N, sample.data(), state);
        environment::rounding(decoding_mode);
        counts c; rd_vector(input.data(), state, c);
        report_rd<T>("ALP_selected_RD", state, c);
        return c.mismatches != 0;
    }
    std::array<I<T>, N> integers{}, packed{}, unpacked{}; std::array<std::uint16_t, N> positions{};
    std::uint16_t count = 0;
    decimal_encode(input.data(), exceptions.data(), positions.data(), &count, integers.data(), state);
    const auto encode_flags = std::fetestexcept(FE_ALL_EXCEPT);
    alp::encoder<T>::analyze_ffor(integers.data(), state.bit_width, &state.for_base);
    ffor::ffor(integers.data(), packed.data(), state.bit_width, &state.for_base);
    unffor::unffor(packed.data(), unpacked.data(), state.bit_width, &state.for_base);
    for (unsigned i = 0; i < N; ++i) need(unpacked[i] == integers[i], "ALP integer pack/unpack");
    environment::rounding(decoding_mode); std::feclearexcept(FE_ALL_EXCEPT);
    alp::decoder<T>::decode(unpacked.data(), state.fac, state.exp, output.data());
    alp::decoder<T>::patch_exceptions(output.data(), exceptions.data(), positions.data(), &count);
    const auto decode_flags = std::fetestexcept(FE_ALL_EXCEPT);
    // Restore ordinary comparison environment; expectations are stored raw bits.
    environment::rounding(0); environment::flush(false);
    unsigned mismatches = 0;
    for (unsigned i = 0; i < N; ++i) mismatches += bits(input[i]) != bits(output[i]);
    std::printf("{\"path\":\"ALP\",\"type\":\"%s\",\"shape\":\"%s\",\"enc_round\":%u,\"dec_round\":%u,\"flush\":%s,"
        "\"exponent\":%u,\"factor\":%u,\"exceptions\":%u,\"mismatches\":%u,\"encode_flags\":%d,\"decode_flags\":%d}\n",
        type_name<T>(), shape.c_str(), encoding_mode, decoding_mode, flush ? "true" : "false", state.exp, state.fac,
        count, mismatches, encode_flags, decode_flags);
    return mismatches != 0;
}
} // namespace
int main(int argc, char** argv) {
    need(argc >= 2, "mode required"); const std::string mode = argv[1];
    if (mode == "rd-corpus") { need(argc == 3, "corpus path"); return rd_corpus(argv[2]); }
    if (mode == "alp-corpus") {
        need(argc == 3, "ALP corpus path");
        std::array<double, N> input{};
        std::ifstream stream(argv[2], std::ios::binary);
        stream.read(reinterpret_cast<char*>(input.data()), sizeof(input));
        need(bool(stream) && std::endian::native == std::endian::little, "complete original IEEE corpus prefix");
        return decimal<double>("plain", 0, 0, false, input.data());
    }
    need(argc >= 3, "type required"); const bool fp32 = std::string(argv[2]) == "float";
    need(fp32 || std::string(argv[2]) == "double", "type");
    if (mode == "rd" || mode == "rd-raw") {
        need(argc == 5, "RD cardinality and population"); const auto d = unsigned(std::stoul(argv[3])), p = unsigned(std::stoul(argv[4]));
        return fp32 ? rd_case<float>(d, p, mode == "rd-raw") : rd_case<double>(d, p, mode == "rd-raw");
    }
    if (mode == "rd-special") {
        need(argc == 5, "rounding and FZ"); const auto r = unsigned(std::stoul(argv[3])); const bool f = std::stoul(argv[4]);
        return fp32 ? rd_special<float>(r, f) : rd_special<double>(r, f);
    }
    if (mode == "alp") {
        need(argc == 7, "ALP shape/encode rounding/decode rounding/FZ"); const auto e = unsigned(std::stoul(argv[4])), d = unsigned(std::stoul(argv[5]));
        return fp32 ? decimal<float>(argv[3], e, d, bool(std::stoul(argv[6]))) : decimal<double>(argv[3], e, d, bool(std::stoul(argv[6])));
    }
    need(false, "unknown mode");
}
